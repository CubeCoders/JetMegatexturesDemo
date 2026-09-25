# Documentation captures

`chapel.png` is a wide interior view. `water-gun-light.png` looks slightly down at
the water-gun window's coloured stone sill and surrounding trim. `chapel.gif` is
a fresh 12-second selection of **wider room views**: the approach, the altar and
the return past the side windows and pews. All use the approved weighted palettes
and render James D. Lambert's scene under [its retained MIT licence](../../LICENSE.n64brew2023).

The revised 90-second firmware tour also includes close window reveals, bringing
the baked light on the stonework into view before returning to the architecture.
Lighting is part of the textures, not simulated dynamically by the capture.

These are native Jet captures using the ESP32 configuration: 480×320 RGB565,
half-width buffers, alternating fields, painter ordering, the paletted mip chain
and dynamic cached bilinear with nearest fallback. The FPS overlay is absent.
They are not higher-resolution renders or recordings of the physical S3 LCD.

The capture renders all preceding tour fields to populate the cache. For
repeatable host output, filling is limited to 64 rows / at most one tile per field
rather than a host-dependent 1 ms budget. A nominal 60-field/s timeline is paired
to 30 images/s, then encoded at 10 GIF frames/s. This demonstrates appearance and
motion, **not actual S3 timing or the exact moment each hardware tile warms**.
GIF's 256-colour palette adds encoding quantization; the PNGs retain the expanded
RGB565 images. The [manifest](manifest.json) records settings, selected times,
the asset and camera-source hashes, and media file hashes.

From the repository root, after the README's native build:

```sh
./build-native/chapel_capture build-capture
python tools/make-media.py build-capture
```

For a multi-configuration Windows generator, use
`build-native/Release/chapel_capture.exe`. FFmpeg must be on PATH, or supply
`--ffmpeg /path/to/ffmpeg`. The raw intermediate is ignored by Git.

Optional inspection modes write stills without a video:

```sh
./build-native/chapel_capture build-inspect --inspect
./build-native/chapel_capture build-pose --pose -3.65 2.35 -3.3 -110 26 75
```

`--inspect` saves a frame every two seconds of the full tour. `--pose` takes
position in metres, yaw/pitch in degrees and horizontal FOV in degrees, then
warms a stationary view for 240 fields before saving `pose.ppm`. Positive pitch
looks down. These options help review framing without changing the firmware.
