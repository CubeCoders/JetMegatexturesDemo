# Documentation captures

`chapel.png` is one selected tour frame; `chapel.gif` is a 12-second selection
of the interior approach, side window and round window. Both render the derived
James D. Lambert scene under [its retained MIT licence](../../LICENSE.n64brew2023).

These are native Jet captures using the ESP32 configuration: 480×320 RGB565,
half-width buffers, alternating fields, painter ordering, the paletted mip chain
and dynamic cached bilinear with nearest fallback. The FPS overlay is absent.
They are not higher-resolution renders or recordings of the physical S3 LCD.

The capture renders preceding tour fields to populate the cache. For repeatable
host output, filling is limited to 64 rows / at most one tile per field rather
than a host-dependent 1 ms budget. A nominal 60-field/s timeline is paired to
30 images/s, then encoded at 12 GIF frames/s. This demonstrates appearance and
motion, **not actual S3 timing or the exact moment each hardware tile warms**.
GIF's 256-colour palette adds encoding quantization; the PNG retains the expanded
RGB565 image. The manifest records settings, selected times and file hashes.

From the repository root, after the README's native build:

```sh
./build-native/chapel_capture build-capture
python tools/make-media.py build-capture
```

For a multi-configuration Windows generator, use
`build-native/Release/chapel_capture.exe`. FFmpeg must be on PATH, or supply
`--ffmpeg /path/to/ffmpeg`. The raw intermediate is ignored by Git.
