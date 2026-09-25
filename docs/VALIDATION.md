# Validation

## Hardware reference

Measured on the ESP32-S3 / ST7796 / 80 MHz SPI board described in the README,
with 16 MiB flash, 8 MiB octal PSRAM and the button scan enabled. The public
project defaults that board-specific scan off; rendering settings are unchanged.

The dynamic hot-cache firmware was verified against its flashed image before
a 195-second serial capture on 2026-09-25. SHA-256:
`b89b9af020dc10cff20700731b601afbcca7f11b3ac03530bf716743f6a74c5b`.
This is the measured prototype binary, not a claim of byte identity with a
fresh build (paths, toolchains and the public input default can change it).

- Two full 90-second tour wraps; 169 cadence observations, 32.56–59.98 fields/s.
- Stable free memory after warmup: 34,243 bytes internal, 1,858,852 bytes PSRAM.
- No logged panic, watchdog or heap-corruption errors.
- Incremental fills and actual eviction exercised; maximum cache update 3,239 µs.
- Sampled hit percentages range from 6.1% to 100%; they are four-field windows.
- Feedback tables are bounded. Under heavy demand they can drop requests;
  this delays admission but does not invalidate sampling or frame pixels.

The [hot-cache report](measurements/hot-playback.json) retains the observations.
[Three-point comparisons](measurements/threepoint-bench.json) and
[fixed-prefilter comparisons](measurements/prefilter-bench.json) retain the earlier
same-binary view measurements. Fixed-prefilter pixels matched ordinary bilinear
exactly and reduced close-window work from roughly 26 ms to 9–11 ms. That proof
used a separate 2 MiB table, not the current 1 MiB dynamic pool.

## Reproducible checks

The native CTest suite uses the actual renderer and checked-in asset pack. It
checks independent nearest/bilinear/three-point oracles, exact prefilter parity,
nearest fallback, unpublished partial tiles, material/mip keys, eviction beyond
128 slots, bounded feedback overflow, 200 moving-camera fields, worker-band
boundaries, guard pixels and serial/parallel equality. Camera checks cover input
takeover, modifiers, bounds, return chords and tour continuity. Additional engine
checks exercise ordinary texture rendering alongside the new tiled path.

Run the commands in the README to reproduce these checks. Native execution does
not predict S3 timing. Documentation captures use ESP32 visual settings; their
nominal playback clock is not a benchmark.

## Publication build

The clean public project builds from its checked-in defaults with ESP-IDF 6.0.1
and Xtensa GCC 15.2.0. All **10 native CTest checks passed** on MSVC 19.51 in
Release mode with assertions enabled. The new portable capture was rendered and
its PNG and GIF frames visually inspected. Asset-pack and upstream licence
contents were verified against the prototype and pinned upstream source.
The [publication manifest](measurements/publication.json) records the fresh binary
and asset hashes. This publication build was not flashed; the already validated
hot-cache firmware remains on the test board.
