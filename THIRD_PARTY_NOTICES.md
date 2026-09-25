# Third-party notices

## James D. Lambert — N64 megatextures

This demo adapts the chapel scene and baked artwork from
[lambertjamesd/n64brew2023](https://github.com/lambertjamesd/n64brew2023), pinned to
`8841ddf3e7d591af17287391f4b3b8728064c7bf`. James' N64 renderer and its tiled texture
residency approach inspired this experiment. The Jet renderer, ESP32 runtime,
palette conversion and incremental bilinear-result cache are the ESP32 adaptation.

**Copyright (c) 2023 James D. Lambert.** The complete upstream MIT text is
retained in [LICENSE.n64brew2023](LICENSE.n64brew2023). It applies to the derived
scene data, texture pack and artwork visible in the documentation media.
See [asset provenance](esp32-streaming-chapel/generated/README.md) for the exact
source paths and modifications. Retain this notice and the upstream licence when
redistributing the derived assets, including with firmware.

## Oklab colour conversion

The offline palette helper adapts Björn Ottosson's Oklab conversion formulas from
[A perceptual color space for image processing](https://bottosson.github.io/posts/oklab/).
The author publishes the example conversion code as public domain (with MIT as
an alternative). We use the public-domain grant and retain this attribution.
The palette importance weights, RGB565-constrained fitting and training cleanup
are part of this adaptation. Error diffusion uses Pillow's standard
Floyd–Steinberg mode.

## Jet

[CubeCoders/Jet](https://github.com/CubeCoders/Jet) is included as a pinned Git
submodule. Its licence remains at [components/Jet/LICENSE](components/Jet/LICENSE).

## LovyanGFX

The [CubeCoders fork](https://github.com/CubeCoders/LovyanGFX) of
[lovyan03/LovyanGFX](https://github.com/lovyan03/LovyanGFX) supplies LCD support and
the DMA changes used by this runtime. Its complete notices, including bundled
Adafruit-derived code, are retained in
[components/LovyanGFX/license.txt](components/LovyanGFX/license.txt).
Bundled font and example assets retain their adjacent notices in the submodule.

CubeCoders' example code is [MIT licensed](LICENSE). That licence does not replace
the notices above. ESP-IDF is an external build dependency, not vendored here.
